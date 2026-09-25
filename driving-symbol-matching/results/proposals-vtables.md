# Proposals from vtables (86 pairs)

Outcomes: propose 143, confirmed 41, propose (create function) 32, conflict 17, stub 9

Skipped (two PS2 vtables claiming one Xbox vtable, or a sheet name cut short):

- PBondCar virtual table: Xbox vtable 0x0018f580 is also paired with PTank virtual table
- PTank virtual table: Xbox vtable 0x0018f580 is also paired with PBondCar virtual table

## conflict (17)

| Xbox | now | proposed | also (folded) | weakest PS2 source | evidence |
|---|---|---|---|---|---|
| 00042060 | EPause::Process | EPause::~EPause |  | ghidra | called by EPause::scalar_deleting_destructor (0x00048440) and stores EPause's vtable |
| 000448b0 | ESetTimer::Process | ESetTimer::~ESetTimer |  | ghidra | called by ESetTimer::scalar_deleting_destructor (0x00048b80) and stores ESetTimer's vtable |
| 00045880 | ESpawnSentry::Process | ESpawnSentry::~ESpawnSentry |  | ghidra | called by ESpawnSentry::scalar_deleting_destructor (0x00048ca0) and stores ESpawnSentry's vtable |
| 00047ed0 | EInsertObjective::~EInsertObjective | EStartTimedAction::scalar_deleting_destructor |  | ghidra | EStartTimedAction slot 0 (PS2 0015c9d8, row 1777, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048440 | EPause::~EPause | EPause::scalar_deleting_destructor |  | ghidra | EPause slot 0 (PS2 00153488, row 1513, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048680 | EBeginDownloadCode::~EBeginDownloadCode | EChangeMaxTraffic::scalar_deleting_destructor | EChangeStage::~EChangeStage | ghidra | EChangeStage slot 0 (PS2 00149870, row 1276, ghidra); EChangeMaxTraffic slot 0 (PS2 001497b8, row 1273, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048700 | EForceNearPedRespawn::~EForceNearPedRespawn | EStartTimer::scalar_deleting_destructor |  | ghidra | EStartTimer slot 0 (PS2 0015cae0, row 1780, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048b80 | ESetTimer::~ESetTimer | ESetTimer::scalar_deleting_destructor |  | ghidra | ESetTimer slot 0 (PS2 001591f8, row 1717, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048ca0 | ESpawnSentry::~ESpawnSentry | ESpawnSentry::scalar_deleting_destructor |  | ghidra | ESpawnSentry slot 0 (PS2 0015b900, row 1756, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004d620 | ERestart::Process | ERestart::~ERestart |  | sheet | called by ERestart::scalar_deleting_destructor (0x0004edc0) and stores ERestart's vtable |
| 0004d7c0 | EResume::Process | EResume::~EResume |  | ghidra | called by EResume::scalar_deleting_destructor (0x0004ede0) and stores EResume's vtable |
| 0004edc0 | ERestart::~ERestart | ERestart::scalar_deleting_destructor |  | sheet | ERestart slot 0 (PS2 001566b0, row 1603, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ede0 | EResume::~EResume | EResume::scalar_deleting_destructor |  | ghidra | EResume slot 0 (PS2 001568f0, row 1606, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00078470 | RCamera::~RCamera | RCamera::scalar_deleting_destructor |  | sheet | RCamera slot 0 (PS2 001a4638, row 2951, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000f7340 | EAGLAnim::FnAnim::~FnAnim | EAGLAnim::FnAnim::scalar_deleting_destructor |  | sheet | EAGLAnim::FnAnim slot 0 (PS2 0026df80, row 6582, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 001008e0 | EAGLAnim::FnDeltaF3::~FnDeltaF3 | EAGLAnim::FnDeltaF3::scalar_deleting_destructor |  | sheet | EAGLAnim::FnDeltaF3 slot 0 (PS2 00270008, row 6624, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00122550 | AStream::~AStream | AStream::scalar_deleting_destructor |  | sheet | AStream slot 0 (PS2 002e8c00, row 8790, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |

## propose (create function) (32)

| Xbox | now | proposed | also (folded) | weakest PS2 source | evidence |
|---|---|---|---|---|---|
| 00023890 | (no function) | AICharacterPassenger::DoNeutral |  | infill-exact-run | AICharacterPassenger slot 8 (PS2 001257c8, row 649, infill-exact-run) |
| 000238c0 | (no function) | AICharacterPassenger::HandleInterrupts |  | infill-exact-run | AICharacterPassenger slot 30 (PS2 00125860, row 654, infill-exact-run) |
| 00027150 | (no function) | AICharacterPedestrian::DoStartled |  | infill-count | AICharacterPedestrian slot 13 (PS2 00127908, row 682, infill-count) |
| 000592b0 | (no function) | AttributeSystem::Kill |  | sheet | AttributeSystem slot 2 (PS2 00173920, row 2289, sheet) |
| 0006ed40 | (no function) | PhysicsNamespace::NameLookup |  | sheet | PhysicsNamespace slot 0 (PS2 001950a0, row 2696, sheet) |
| 0007e930 | (no function) | RLightManager::Kill |  | ghidra | RLightManager slot 2 (PS2 001b04f8, row 3158, ghidra) |
| 000877e0 | (no function) | RPlayerCamera::UpdateCamera |  | infill-exact-run | RPlayerCamera slot 3 (PS2 001b29e0, row 3230, infill-exact-run) |
| 0008b3c0 | (no function) | RRenderDebugViewScreenSpace::Debug |  | sheet | RRenderDebugViewScreenSpace slot 1 (PS2 001c2f78, row 3421, sheet) |
| 0008dce0 | (no function) | RSceneObj::GetRenderOffset |  | infill-exact | RSceneObj slot 9 (PS2 001c92f8, row 3537, infill-exact) |
| 00095b30 | (no function) | RVehicle::RenderShadow |  | sheet | RVehicle slot 4 (PS2 001d3228, row 3712, sheet) |
| 000964b0 | (no function) | RVehicle::UpdatePosition |  | sheet | RVehicle slot 14 (PS2 001d34a8, row 3716, sheet) |
| 0009a480 | (no function) | RColorize::Kill |  | sheet | RColorize slot 2 (PS2 001d9330, row 3904, sheet) |
| 0009ac00 | (no function) | RColorize::Reset |  | sheet | RColorize slot 1 (PS2 001d9390, row 3908, sheet) |
| 0009af50 | (no function) | RDecalManager::Kill |  | sheet | RDecalManager slot 2 (PS2 001dae20, row 3935, sheet) |
| 0009ff60 | (no function) | RLightning::Kill |  | sheet | RLightning slot 2 (PS2 001e33e0, row 4066, sheet) |
| 000a0b30 | (no function) | RMissileCam::Kill |  | sheet | RMissileCam slot 2 (PS2 001e3d70, row 4079, sheet) |
| 000a3290 | (no function) | RParticulate::Kill |  | sheet | RParticulate slot 2 (PS2 001ea078, row 4185, sheet) |
| 000a4550 | (no function) | RPostProcessing::Reset |  | infill-exact | RPostProcessing slot 1 (PS2 001ea3f8, row 4192, infill-exact) |
| 000a4eb0 | (no function) | RPostProcessing::Kill |  | ghidra | RPostProcessing slot 2 (PS2 001ea850, row 4203, ghidra) |
| 000a8470 | (no function) | RWater::Kill |  | sheet | RWater slot 2 (PS2 001f0ad8, row 4288, sheet) |
| 000a9eb0 | (no function) | RGlareManager::Kill |  | sheet | RGlareManager slot 2 (PS2 001f2948, row 4308, sheet) |
| 000a9ed0 | (no function) | RGlareManager::Reset |  | ghidra | RGlareManager slot 1 (PS2 001f2990, row 4311, ghidra) |
| 000febd0 | (no function) | EAGLAnim::FnDeltaF1::EvalSQT |  | infill-count | EAGLAnim::FnDeltaF1 slot 6 (PS2 002701b8, row 6630, infill-count) |
| 001051a0 | (no function) | EAGLAnim::FnRunBlender::Eval |  | sheet | EAGLAnim::FnRunBlender slot 3 (PS2 00279398, row 6881, sheet) |
| 00105780 | (no function) | EAGLAnim::FnRunBlender::EvalPhase |  | infill-exact-run | EAGLAnim::FnRunBlender slot 7 (PS2 00279498, row 6884, infill-exact-run) |
| 00129b00 | (no function) | AUltraLite::Play |  | infill-exact-run | AUltraLite slot 3 (PS2 002e3c40, row 8743, infill-exact-run) |
| 00129cd0 | (no function) | AUltraLite::PlayMotor |  | infill-exact-run | AUltraLite slot 9 (PS2 002e3c90, row 8744, infill-exact-run) |
| 0012b540 | (no function) | ASnowMobile::Play |  | infill-exact-run | ASnowMobile slot 3 (PS2 002edd88, row 8870, infill-exact-run) |
| 0012bb60 | (no function) | ASubmersible::SetCreakLevel |  | infill-exact | ASubmersible slot 9 (PS2 002e8650, row 8783, infill-exact) |
| 0012c640 | (no function) | APlayerTank::Play |  | infill-exact-run | APlayerTank slot 3 (PS2 002f1000, row 8921, infill-exact-run) |
| 0012c930 | (no function) | AHelicopter::PlayLanding |  | infill-exact | AHelicopter slot 4 (PS2 002f6900, row 9010, infill-exact) |
| 0012c950 | (no function) | AHelicopter::Play |  | infill-exact | AHelicopter slot 3 (PS2 002f6938, row 9011, infill-exact) |

## propose (143)

| Xbox | now | proposed | also (folded) | weakest PS2 source | evidence |
|---|---|---|---|---|---|
| 00011100 | FUN_00011100 | StandardAnimationController::Update |  | ghidra | StandardAnimationController slot 0 (PS2 00107490, row 20, ghidra) |
| 000125f0 | FUN_000125f0 | StandardAnimationController::scalar_deleting_destructor |  | infill-exact | StandardAnimationController slot 1 (PS2 0010bba8, row 107, infill-exact); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0001c090 | FUN_0001c090 | ABaseSound::scalar_deleting_destructor |  | ghidra | ABaseSound slot 0 (PS2 002fcb28, row 9108, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0001d640 | FUN_0001d640 | AICharacterEnemyDriver::~AICharacterEnemyDriver |  | sheet | called by AICharacterEnemyDriver::scalar_deleting_destructor (0x0001d650) and stores AICharacterEnemyDriver's vtable |
| 0001d650 | FUN_0001d650 | AICharacterEnemyDriver::scalar_deleting_destructor |  | sheet | AICharacterEnemyDriver slot 0 (PS2 0011ad88, row 470, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0001d680 | FUN_0001d680 | AICharacterEnemyDriver::DoInitial |  | infill-exact-run | AICharacterEnemyDriver slot 7 (PS2 0011ade0, row 471, infill-exact-run) |
| 000236c0 | FUN_000236c0 | AICharacterPassenger::~AICharacterPassenger |  | sheet | called by AICharacterPassenger::scalar_deleting_destructor (0x000236d0) and stores AICharacterPassenger's vtable |
| 000236d0 | FUN_000236d0 | AICharacterPassenger::scalar_deleting_destructor |  | sheet | AICharacterPassenger slot 0 (PS2 00125738, row 647, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00023b70 | FUN_00023b70 | AICharacterPedestrian::DoDead |  | ghidra | AICharacterPedestrian slot 29 (PS2 00128488, row 687, ghidra) |
| 00023f50 | FUN_00023f50 | AICharacterPedestrian::~AICharacterPedestrian |  | ghidra | called by AICharacterPedestrian::scalar_deleting_destructor (0x00024700) and stores AICharacterPedestrian's vtable |
| 00024700 | FUN_00024700 | AICharacterPedestrian::scalar_deleting_destructor |  | ghidra | AICharacterPedestrian slot 0 (PS2 00125b48, row 659, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00025ef0 | FUN_00025ef0 | AICharacterPedestrian::DoNeutral |  | infill-count | AICharacterPedestrian slot 8 (PS2 00127708, row 677, infill-count) |
| 00025f40 | FUN_00025f40 | AICharacterPedestrian::DoIdling |  | infill-count | AICharacterPedestrian slot 9 (PS2 00127798, row 678, infill-count) |
| 00025fc0 | FUN_00025fc0 | AICharacterPedestrian::DoDying |  | infill-count | AICharacterPedestrian slot 22 (PS2 001280c8, row 685, infill-count) |
| 00026290 | FUN_00026290 | AICharacterPedestrian::DoTurning |  | infill-count | AICharacterPedestrian slot 24 (PS2 001282e0, row 686, infill-count) |
| 000263d0 | FUN_000263d0 | AICharacterPedestrian::DoLeaning |  | infill-exact-run | AICharacterPedestrian slot 28 (PS2 001284e0, row 688, infill-exact-run) |
| 000270b0 | FUN_000270b0 | AICharacterPedestrian::DoWalking |  | infill-count | AICharacterPedestrian slot 10 (PS2 00127840, row 679, infill-count) |
| 00027240 | FUN_00027240 | AICharacterPedestrian::DoAvoiding |  | infill-count | AICharacterPedestrian slot 12 (PS2 00127a30, row 683, infill-count) |
| 000274c0 | FUN_000274c0 | AICharacterPedestrian::DoDodging |  | infill-count | AICharacterPedestrian slot 14 (PS2 00127d58, row 684, infill-count) |
| 00035b90 | FUN_00035b90 | AIVehicle::scalar_deleting_destructor |  | infill-exact-run | AIVehicle slot 0 (PS2 0013df50, row 939, infill-exact-run); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00037a10 | FUN_00037a10 | ASceneObj::PlayCollision |  | infill-exact | ASentry slot 5 (PS2 002f05b0, row 8894, infill-exact) |
| 0004eb40 | FUN_0004eb40 | ECollision::scalar_deleting_destructor |  | ghidra | ECollision slot 0 (PS2 00149ed8, row 1288, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000592d0 | FUN_000592d0 | AttributeSystem::~AttributeSystem |  | sheet | called by AttributeSystem::scalar_deleting_destructor (0x00059530) and stores AttributeSystem's vtable |
| 00059530 | FUN_00059530 | AttributeSystem::scalar_deleting_destructor |  | sheet | AttributeSystem slot 0 (PS2 0016c880, row 2211, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0005dc60 | FUN_0005dc60 | Human::~Human |  | sheet | called by Human::scalar_deleting_destructor (0x0005dea0) and stores Human's vtable |
| 0005dd00 | FUN_0005dd00 | Human::Simulate |  | sheet | Human slot 4 (PS2 0017f980, row 2486, sheet) |
| 0005dea0 | FUN_0005dea0 | Human::scalar_deleting_destructor |  | sheet | Human slot 0 (PS2 0017f4c0, row 2481, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0005f350 | FUN_0005f350 | Missile::~Missile |  | sheet | called by Missile::scalar_deleting_destructor (0x0005f6a0) and stores Missile's vtable |
| 0005f6a0 | FUN_0005f6a0 | Missile::scalar_deleting_destructor |  | sheet | Missile slot 0 (PS2 001818c8, row 2510, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00060b60 | FUN_00060b60 | Newton::~Newton |  | ghidra | called by Newton::scalar_deleting_destructor (0x000612f0) and stores Newton's vtable |
| 000612f0 | FUN_000612f0 | Newton::scalar_deleting_destructor |  | ghidra | Newton slot 0 (PS2 00184040, row 2529, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0006e290 | FUN_0006e290 | PHelicopter::~PHelicopter |  | ghidra | called by PHelicopter::scalar_deleting_destructor (0x0006e300) and stores PHelicopter's vtable |
| 0006e300 | FUN_0006e300 | PHelicopter::scalar_deleting_destructor |  | ghidra | PHelicopter slot 0 (PS2 001937b0, row 2663, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0006e330 | FUN_0006e330 | PHelicopter::ApplyDamage |  | infill-exact-run | PHelicopter slot 2 (PS2 00193938, row 2665, infill-exact-run) |
| 0006f050 | (no function) | PhysicsNamespace::scalar_deleting_destructor |  | sheet | PhysicsNamespace slot 1 (PS2 00195070, row 2695, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0006f7f0 | FUN_0006f7f0 | PhysicsObject::scalar_deleting_destructor |  | ghidra | PhysicsObject slot 0 (PS2 00195568, row 2703, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000735d0 | FUN_000735d0 | Sentry::scalar_deleting_destructor |  | sheet | Sentry slot 0 (PS2 0019b520, row 2807, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00073f80 | FUN_00073f80 | Sentry::ApplyDamage |  | ghidra | Sentry slot 2 (PS2 0019b5a8, row 2808, ghidra) |
| 000744d0 | FUN_000744d0 | Sentry::Simulate |  | sheet | Sentry slot 4 (PS2 0019b9d0, row 2809, sheet) |
| 00075300 | FUN_00075300 | Smackable::~Smackable |  | sheet | called by Smackable::scalar_deleting_destructor (0x00075530) and stores Smackable's vtable |
| 00075530 | FUN_00075530 | Smackable::scalar_deleting_destructor |  | sheet | Smackable slot 0 (PS2 0019e590, row 2838, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0007daa0 | FUN_0007daa0 | RFog::Kill |  | sheet | RFog slot 2 (PS2 001acea0, row 3099, sheet) |
| 0007dac0 | FUN_0007dac0 | RFog::scalar_deleting_destructor | RColorize::~RColorize, RWater::~RWater, RGlareManager::~RGlareManager | infill-exact | RFog slot 0 (PS2 001ac818, row 3087, sheet); RColorize slot 0 (PS2 001d8440, row 3894, sheet); RWater slot 0 (PS2 001f0378, row 4281, infill-exact) ... |
| 0007e8c0 | FUN_0007e8c0 | RLightManager::~RLightManager |  | sheet | called by RLightManager::scalar_deleting_destructor (0x0007f450) and stores RLightManager's vtable |
| 0007f450 | FUN_0007f450 | RLightManager::scalar_deleting_destructor |  | sheet | RLightManager slot 0 (PS2 001aeba8, row 3128, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00081bf0 | FUN_00081bf0 | RPlayerCamera::~RPlayerCamera |  | sheet | called by RPlayerCamera::scalar_deleting_destructor (0x00084a30) and stores RPlayerCamera's vtable |
| 00084a30 | FUN_00084a30 | RPlayerCamera::scalar_deleting_destructor |  | sheet | RPlayerCamera slot 0 (PS2 001b2728, row 3228, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0008b4d0 | FUN_0008b4d0 | RRenderDebugViewPerspective::scalar_deleting_destructor |  | infill-exact-run | RRenderDebugViewPerspective slot 0 (PS2 001c3758, row 3424, infill-exact-run); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0008b4f0 | FUN_0008b4f0 | RRenderDebugViewPerspective::PreRender |  | ghidra | RRenderDebugViewPerspective slot 2 (PS2 001c2e08, row 3416, ghidra) |
| 0008b510 | FUN_0008b510 | RRenderDebugViewScreenSpace::scalar_deleting_destructor |  | infill-exact | RRenderDebugViewScreenSpace slot 0 (PS2 001c3830, row 3428, infill-exact); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0008b530 | FUN_0008b530 | RRenderDebugViewScreenSpace::DoRender |  | sheet | RRenderDebugViewScreenSpace slot 4 (PS2 001c2ec0, row 3419, sheet) |
| 0008c950 | FUN_0008c950 | RRenderWorldCamera::~RRenderWorldCamera |  | ghidra | called by RRenderWorldCamera::scalar_deleting_destructor (0x0008ccf0) and stores RRenderWorldCamera's vtable |
| 0008c990 | FUN_0008c990 | RRenderWorldCamera::PreRender |  | ghidra | RRenderWorldCamera slot 2 (PS2 001c5458, row 3464, ghidra) |
| 0008ccf0 | FUN_0008ccf0 | RRenderWorldCamera::scalar_deleting_destructor |  | ghidra | RRenderWorldCamera slot 0 (PS2 001c53b8, row 3462, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0008f270 | FUN_0008f270 | RSceneObj::UpdatePosition |  | sheet | RSceneObj slot 14 (PS2 001c8d70, row 3533, sheet) |
| 000905a0 | FUN_000905a0 | RSceneObj::~RSceneObj |  | sheet | called by RSceneObj::scalar_deleting_destructor (0x000908b0) and stores RSceneObj's vtable |
| 000908b0 | FUN_000908b0 | RSceneObj::scalar_deleting_destructor |  | sheet | RSceneObj slot 0 (PS2 001c7270, row 3498, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000959b0 | FUN_000959b0 | RVehicle::TriggerIlluminate |  | infill-exact-run | RVehicle slot 12 (PS2 001d2aa0, row 3702, infill-exact-run) |
| 00096430 | FUN_00096430 | RVehicle::~RVehicle |  | sheet | called by RVehicle::scalar_deleting_destructor (0x000967f0) and stores RVehicle's vtable |
| 000967f0 | FUN_000967f0 | RVehicle::scalar_deleting_destructor |  | sheet | RVehicle slot 0 (PS2 001d2508, row 3696, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00096c60 | FUN_00096c60 | RViewCamera::scalar_deleting_destructor |  | sheet | RViewCamera slot 0 (PS2 001d4238, row 3735, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000979f0 | FUN_000979f0 | RWorldCamera::~RWorldCamera |  | ghidra | called by RWorldCamera::scalar_deleting_destructor (0x00097eb0) and stores RWorldCamera's vtable |
| 00097eb0 | FUN_00097eb0 | RWorldCamera::scalar_deleting_destructor |  | ghidra | RWorldCamera slot 0 (PS2 001d54a8, row 3792, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0009af70 | FUN_0009af70 | RDecalManager::Reset |  | infill-count | RDecalManager slot 1 (PS2 001da558, row 3929, infill-count) |
| 0009b3c0 | FUN_0009b3c0 | RDecalManager::~RDecalManager |  | infill-count | called by RDecalManager::scalar_deleting_destructor (0x0009b450) and stores RDecalManager's vtable |
| 0009b450 | FUN_0009b450 | RDecalManager::scalar_deleting_destructor |  | infill-count | RDecalManager slot 0 (PS2 001da310, row 3924, infill-count); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0009de00 | FUN_0009de00 | RGain::~RGain |  | sheet | called by RGain::scalar_deleting_destructor (0x0009e120) and stores RGain's vtable |
| 0009dea0 | FUN_0009dea0 | RGain::Reset |  | sheet | RGain slot 1 (PS2 001df830, row 4000, sheet) |
| 0009e120 | FUN_0009e120 | RGain::scalar_deleting_destructor |  | sheet | RGain slot 0 (PS2 001df800, row 3999, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0009e150 | FUN_0009e150 | RLensFlareManager::Reset |  | sheet | RLensFlareManager slot 1 (PS2 001dfcf8, row 4013, sheet) |
| 0009e1b0 | FUN_0009e1b0 | RLensFlareManager::~RLensFlareManager |  | sheet | called by RLensFlareManager::scalar_deleting_destructor (0x0009e520) and stores RLensFlareManager's vtable |
| 0009e520 | FUN_0009e520 | RLensFlareManager::scalar_deleting_destructor |  | sheet | RLensFlareManager slot 0 (PS2 001dfd38, row 4014, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0009fe30 | FUN_0009fe30 | RLightning::Reset |  | infill-count | RLightning slot 1 (PS2 001e0a70, row 4027, infill-count) |
| 0009fec0 | FUN_0009fec0 | RLightning::~RLightning |  | infill-count | called by RLightning::scalar_deleting_destructor (0x000a0000) and stores RLightning's vtable |
| 000a0000 | FUN_000a0000 | RLightning::scalar_deleting_destructor |  | infill-count | RLightning slot 0 (PS2 001e09d0, row 4026, infill-count); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000a0df0 | FUN_000a0df0 | RMissileCam::~RMissileCam |  | sheet | called by RMissileCam::scalar_deleting_destructor (0x000a0e70) and stores RMissileCam's vtable |
| 000a0e70 | FUN_000a0e70 | RMissileCam::scalar_deleting_destructor |  | sheet | RMissileCam slot 0 (PS2 001e3688, row 4074, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000a3240 | FUN_000a3240 | RParticulate::~RParticulate |  | sheet | called by RParticulate::scalar_deleting_destructor (0x000a3fa0) and stores RParticulate's vtable |
| 000a3fa0 | FUN_000a3fa0 | RParticulate::scalar_deleting_destructor |  | sheet | RParticulate slot 0 (PS2 001e9220, row 4175, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000a4f70 | FUN_000a4f70 | RPostProcessing::~RPostProcessing |  | sheet | called by RPostProcessing::scalar_deleting_destructor (0x000a5060) and stores RPostProcessing's vtable |
| 000a5060 | FUN_000a5060 | RPostProcessing::scalar_deleting_destructor |  | sheet | RPostProcessing slot 0 (PS2 001ea328, row 4191, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000a6810 | FUN_000a6810 | RSniperZoom::~RSniperZoom |  | sheet | called by RSniperZoom::scalar_deleting_destructor (0x000a6a50) and stores RSniperZoom's vtable |
| 000a6870 | FUN_000a6870 | RSniperZoom::Kill |  | ghidra | RSniperZoom slot 2 (PS2 001edd80, row 4249, ghidra) |
| 000a6890 | FUN_000a6890 | RSniperZoom::Reset |  | sheet | RSniperZoom slot 1 (PS2 001eddf8, row 4252, sheet) |
| 000a6a50 | FUN_000a6a50 | RSniperZoom::scalar_deleting_destructor |  | sheet | RSniperZoom slot 0 (PS2 001ed570, row 4242, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000e00d0 | FUN_000e00d0 | GHud::scalar_deleting_destructor |  | sheet | GHud slot 0 (PS2 0023c560, row 5307, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000e3ad0 | FUN_000e3ad0 | GSubtitles::scalar_deleting_destructor |  | sheet | GSubtitles slot 0 (PS2 0024a168, row 5449, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000f70f0 | FUN_000f70f0 | EAGLAnim::FnCompoundChannel::GetTargetCheckSum |  | infill-exact-run | EAGLAnim::FnCompoundChannel slot 1 (PS2 00266390, row 6427, infill-exact-run) |
| 000f7100 | FUN_000f7100 | EAGLAnim::FnCompoundChannel::GetLength |  | infill-exact-run | EAGLAnim::FnCompoundChannel slot 4 (PS2 002663b0, row 6430, infill-exact-run) |
| 000f7140 | FUN_000f7140 | EAGLAnim::FnCompoundChannel::GetAttributes |  | infill-exact-run | EAGLAnim::FnCompoundChannel slot 0 (PS2 00266410, row 6432, infill-exact-run) |
| 000f72f0 | FUN_000f72f0 | EAGLAnim::FnAnim::GetTargetCheckSum |  | infill-exact-run | EAGLAnim::FnPoseMirror slot 1 (PS2 0026dfb8, row 6584, infill-exact-run); EAGLAnim::FnAnim slot 1 (PS2 0026dfb8, row 6584, infill-exact-run); EAGLAnim::FnRunBlender slot 1 (PS2 0026dfb8, row 6584, infill-exact-run) ... |
| 000f7300 | FUN_000f7300 | EAGLAnim::FnAnim::GetLength |  | infill-exact-run | EAGLAnim::FnPoseMirror slot 4 (PS2 0026dfd0, row 6587, infill-exact-run); EAGLAnim::FnRawEventChannel slot 4 (PS2 0026dfd0, row 6587, infill-exact-run); EAGLAnim::FnAnimMemoryMap slot 4 (PS2 0026dfd0, row 6587, infill-exact-run) ... |
| 000f7320 | FUN_000f7320 | EAGLAnim::FnAnim::EvalEvent |  | infill-exact-run | EAGLAnim::FnPoseMirror slot 9 (PS2 0026dff8, row 6592, infill-exact-run); EAGLAnim::FnRawLinearChannel slot 9 (PS2 0026dff8, row 6592, infill-exact-run); EAGLAnim::FnRawStateChan slot 9 (PS2 0026dff8, row 6592, infill-exact-run) ... |
| 000f7410 | FUN_000f7410 | EAGLAnim::FnRawEventChannel::SetAnimMemoryMap |  | infill-exact | EAGLAnim::FnRawEventChannel slot 15 (PS2 00264bf8, row 6379, infill-exact) |
| 000f7430 | FUN_000f7430 | EAGLAnim::FnRawEventChannel::EvalEvent |  | infill-exact | EAGLAnim::FnRawEventChannel slot 9 (PS2 00264c08, row 6380, infill-exact) |
| 000f7460 | FUN_000f7460 | EAGLAnim::FnRawEventChannel::Eval |  | infill-exact | EAGLAnim::FnRawEventChannel slot 3 (PS2 00264c40, row 6381, infill-exact) |
| 000f7480 | FUN_000f7480 | EAGLAnim::FnRawEventChannel::scalar_deleting_destructor |  | infill-exact | EAGLAnim::FnRawEventChannel slot 0 (PS2 00264ba0, row 6378, infill-exact); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000f74b0 | FUN_000f74b0 | EAGLAnim::FnRawEventChannel::~FnRawEventChannel |  | infill-exact | called by EAGLAnim::FnRawEventChannel::scalar_deleting_destructor (0x000f7480) and stores EAGLAnim::FnRawEventChannel's vtable |
| 000f74e0 | FUN_000f74e0 | EAGLAnim::FnRawLinearChannel::Eval |  | sheet | EAGLAnim::FnRawLinearChannel slot 3 (PS2 00264d18, row 6384, sheet) |
| 000f7650 | FUN_000f7650 | EAGLAnim::FnRawLinearChannel::GetLength |  | sheet | EAGLAnim::FnRawLinearChannel slot 4 (PS2 00264f30, row 6385, sheet) |
| 000f7670 | FUN_000f7670 | EAGLAnim::FnRawLinearChannel::scalar_deleting_destructor |  | sheet | EAGLAnim::FnRawLinearChannel slot 0 (PS2 00264cc0, row 6383, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000f76a0 | FUN_000f76a0 | EAGLAnim::FnRawLinearChannel::~FnRawLinearChannel |  | sheet | called by EAGLAnim::FnRawLinearChannel::scalar_deleting_destructor (0x000f7670) and stores EAGLAnim::FnRawLinearChannel's vtable |
| 000f7770 | FUN_000f7770 | EAGLAnim::FnDeltaChan::GetLength |  | infill-exact-run | EAGLAnim::FnDeltaQuatChan slot 4 (PS2 002761e0, row 6823, infill-exact-run); EAGLAnim::FnDeltaLerpChan slot 4 (PS2 002761e0, row 6823, infill-exact-run) |
| 000f78a0 | FUN_000f78a0 | EAGLAnim::FnPoseMirror::Eval |  | sheet | EAGLAnim::FnPoseMirror slot 3 (PS2 002650f0, row 6391, sheet) |
| 000f78f0 | FUN_000f78f0 | EAGLAnim::FnPoseMirror::EvalSQT |  | sheet | EAGLAnim::FnPoseMirror slot 6 (PS2 00265180, row 6392, sheet) |
| 000f7940 | FUN_000f7940 | EAGLAnim::FnPoseMirror::scalar_deleting_destructor |  | sheet | EAGLAnim::FnPoseMirror slot 0 (PS2 002650c0, row 6390, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000f8250 | FUN_000f8250 | EAGLAnim::FnDeltaQuatChan::scalar_deleting_destructor |  | ghidra | EAGLAnim::FnDeltaQuatChan slot 0 (PS2 002758d0, row 6771, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000f82d0 | FUN_000f82d0 | EAGLAnim::FnPhaseChan::scalar_deleting_destructor |  | infill-exact-run | EAGLAnim::FnPhaseChan slot 0 (PS2 00277538, row 6837, infill-exact-run); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000faac0 | FUN_000faac0 | EAGLAnim::FnAnimMemoryMap::GetAnimMemoryMap |  | sheet | EAGLAnim::FnRawLinearChannel slot 17 (PS2 0026dd58, row 6556, sheet); EAGLAnim::FnRawEventChannel slot 17 (PS2 0026dd58, row 6556, sheet); EAGLAnim::FnCompoundChannel slot 17 (PS2 0026dd58, row 6556, sheet) ... |
| 000faad0 | FUN_000faad0 | EAGLAnim::FnAnimMemoryMap::GetAnimMemoryMap |  | infill-exact | EAGLAnim::FnRawLinearChannel slot 16 (PS2 0026dd50, row 6555, infill-exact); EAGLAnim::FnRawEventChannel slot 16 (PS2 0026dd50, row 6555, infill-exact); EAGLAnim::FnCompoundChannel slot 16 (PS2 0026dd50, row 6555, infill-exact) ... |
| 000faaf0 | FUN_000faaf0 | EAGLAnim::FnAnimMemoryMap::scalar_deleting_destructor |  | sheet | EAGLAnim::FnAnimMemoryMap slot 0 (PS2 0026dd18, row 6553, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000fd4c0 | FUN_000fd4c0 | EAGLAnim::FnPhaseChan::Eval |  | infill-exact-run | EAGLAnim::FnPhaseChan slot 3 (PS2 00277360, row 6836, infill-exact-run) |
| 000ff1e0 | FUN_000ff1e0 | EAGLAnim::FnDeltaF1::scalar_deleting_destructor |  | sheet | EAGLAnim::FnDeltaF1 slot 0 (PS2 00271710, row 6645, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000ff210 | FUN_000ff210 | EAGLAnim::FnDeltaF1::~FnDeltaF1 |  | sheet | called by EAGLAnim::FnDeltaF1::scalar_deleting_destructor (0x000ff1e0) and stores EAGLAnim::FnDeltaF1's vtable |
| 000ff2b0 | FUN_000ff2b0 | EAGLAnim::FnDeltaF3::Eval |  | infill-count | EAGLAnim::FnDeltaF3 slot 3 (PS2 00270128, row 6627, infill-count) |
| 000ffce0 | FUN_000ffce0 | EAGLAnim::FnDeltaF3::EvalWeights |  | infill-count | EAGLAnim::FnDeltaF3 slot 10 (PS2 00270158, row 6628, infill-count) |
| 000ffd00 | FUN_000ffd00 | EAGLAnim::FnDeltaF3::EvalVel2D |  | infill-count | EAGLAnim::FnDeltaF3 slot 8 (PS2 00270188, row 6629, infill-count) |
| 00100880 | FUN_00100880 | EAGLAnim::FnDeltaF3::SetAnimMemoryMap |  | infill-count | EAGLAnim::FnDeltaF3 slot 15 (PS2 002700d0, row 6625, infill-count) |
| 001008a0 | FUN_001008a0 | EAGLAnim::FnDeltaF3::GetLength |  | infill-count | EAGLAnim::FnDeltaF3 slot 4 (PS2 002700e8, row 6626, infill-count) |
| 00100910 | FUN_00100910 | EAGLAnim::FnDeltaF3::~FnDeltaF3 |  | sheet | called by EAGLAnim::FnDeltaF3::scalar_deleting_destructor (0x001008e0) and stores EAGLAnim::FnDeltaF3's vtable |
| 00101f00 | FUN_00101f00 | EAGLAnim::FnDeltaSingleQ::InitBuffersAsRequired |  | infill-exact-run | EAGLAnim::FnDeltaSingleQ slot 0 (PS2 0026b368, row 6514, infill-exact-run) |
| 0011b650 | FUN_0011b650 | USymbolTable::scalar_deleting_destructor |  | sheet | USymbolTable slot 0 (PS2 002ccfa8, row 8199, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0011c6f0 | FUN_0011c6f0 | ABaseSound::~ABaseSound |  | ghidra | called by ABaseSound::scalar_deleting_destructor (0x0001c090) and stores ABaseSound's vtable |
| 0011dee0 | FUN_0011dee0 | AMenuSoundPriv::Play |  | sheet | AMenuSoundPriv slot 3 (PS2 002f4918, row 8979, sheet) |
| 0011e060 | FUN_0011e060 | AMenuSoundPriv::scalar_deleting_destructor |  | sheet | AMenuSoundPriv slot 0 (PS2 002f4cb8, row 8984, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0011e090 | FUN_0011e090 | AMenuSoundPriv::~AMenuSoundPriv |  | sheet | called by AMenuSoundPriv::scalar_deleting_destructor (0x0011e060) and stores AMenuSoundPriv's vtable |
| 0011ed30 | FUN_0011ed30 | AVehicle::PlayLanding |  | sheet | AVehicle slot 4 (PS2 002dfa00, row 8688, sheet); AUltraLite slot 4 (PS2 002dfa00, row 8688, sheet); ATrafficVehicle slot 4 (PS2 002dfa00, row 8688, sheet) ... |
| 0011f1a0 | FUN_0011f1a0 | AVehicle::PlayCollision |  | ghidra | AVehicle slot 5 (PS2 002e0890, row 8689, ghidra); AUltraLite slot 5 (PS2 002e0890, row 8689, ghidra); ATrafficVehicle slot 5 (PS2 002e0890, row 8689, ghidra) ... |
| 00120a10 | FUN_00120a10 | AVehicle::~AVehicle |  | sheet | called by AVehicle::scalar_deleting_destructor (0x00120d50) and stores AVehicle's vtable |
| 00120d50 | FUN_00120d50 | AVehicle::scalar_deleting_destructor |  | sheet | AVehicle slot 0 (PS2 002e2b60, row 8691, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00122450 | FUN_00122450 | AStream::~AStream |  | sheet | called by AStream::scalar_deleting_destructor (0x00122550) and stores AStream's vtable |
| 00129b20 | FUN_00129b20 | AUltraLite::~AUltraLite |  | sheet | called by AUltraLite::scalar_deleting_destructor (0x0012a410) and stores AUltraLite's vtable |
| 0012a410 | FUN_0012a410 | AUltraLite::scalar_deleting_destructor |  | sheet | AUltraLite slot 0 (PS2 002e3948, row 8742, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0012a890 | FUN_0012a890 | ASnowMobile::~ASnowMobile |  | infill-exact-run | called by ASnowMobile::scalar_deleting_destructor (0x0012b510) and stores ASnowMobile's vtable |
| 0012b510 | FUN_0012b510 | ASnowMobile::scalar_deleting_destructor |  | infill-exact-run | ASnowMobile slot 0 (PS2 002edb08, row 8869, infill-exact-run); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0012c730 | FUN_0012c730 | APlayerTank::~APlayerTank |  | infill-exact-run | called by APlayerTank::scalar_deleting_destructor (0x0012c7c0) and stores APlayerTank's vtable |
| 0012c7c0 | FUN_0012c7c0 | APlayerTank::scalar_deleting_destructor |  | infill-exact-run | APlayerTank slot 0 (PS2 002f1128, row 8922, infill-exact-run); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0012cac0 | FUN_0012cac0 | AHelicopter::~AHelicopter |  | sheet | called by AHelicopter::scalar_deleting_destructor (0x0012cb60) and stores AHelicopter's vtable |
| 0012cb60 | FUN_0012cb60 | AHelicopter::scalar_deleting_destructor |  | sheet | AHelicopter slot 0 (PS2 002f6b08, row 9012, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0012d750 | FUN_0012d750 | ASentry::~ASentry |  | infill-exact-run | called by ASentry::scalar_deleting_destructor (0x0012d7c0) and stores ASentry's vtable |
| 0012d7c0 | FUN_0012d7c0 | ASentry::scalar_deleting_destructor |  | infill-exact-run | ASentry slot 0 (PS2 002effe8, row 8889, infill-exact-run); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0012ffb0 | FUN_0012ffb0 | ARaceEngine::Play |  | sheet | ARaceEngine slot 0 (PS2 002ff320, row 9178, sheet) |
| 001306b0 | FUN_001306b0 | ARaceEngine::scalar_deleting_destructor |  | sheet | ARaceEngine slot 1 (PS2 002ff998, row 9179, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |

## stub (9)

| Xbox | now | proposed | also (folded) | weakest PS2 source | evidence |
|---|---|---|---|---|---|
| 00015770 | FUN_00015770 | AICharacter::PlayZoneInjuryAnim | EAGLAnim::FnAnim::Eval, ASceneObj::PlayLanding | infill-exact-run | AICharacterBond slot 6 (PS2 00119138, row 377, infill-exact-run); AICharacterEnemyDriver slot 6 (PS2 00119138, row 377, infill-exact-run); AICharacterPassenger slot 6 (PS2 00119138, row 377, infill-exact-run) ... |
| 00017550 | dummyNullFunction | PhysicsObject::SetInShock | RSceneObj::SetViewDrawList, RSceneObj::TransformToWorldSpace, RSceneObj::TransformPointToWorldSpace, RSceneObj::TransformMatrixToWorldSpace +2 | infill-exact-run | Human slot 1 (PS2 00195938, row 2710, ghidra); Missile slot 1 (PS2 00195938, row 2710, ghidra); Newton slot 1 (PS2 00195938, row 2710, ghidra) ... |
| 0001c080 | Generic_FuncReturnsFalse | AICharacter::IsTooFarAway | AUltraLite::IsTracked, ASceneObj::IsStillActive, ASceneObj::IsTracked, ASceneObj::IsAirborne +1 | infill-exact-run | AICharacterBond slot 31 (PS2 00119850, row 444, infill-exact); AICharacterEnemyDriver slot 31 (PS2 00119850, row 444, infill-exact); AICharacterPassenger slot 31 (PS2 00119850, row 444, infill-exact) ... |
| 00097a90 | PhysicsObject::ComputeImpulse | PhysicsObject::ComputeImpulse | RWorldCamera::CameraInputCallback | infill-exact-run | Human slot 6 (PS2 00195970, row 2713, ghidra); Missile slot 6 (PS2 00195970, row 2713, ghidra); Newton slot 6 (PS2 00195970, row 2713, ghidra) ... |
| 000b98f0 | StubError | AUltraLite::IsAirborne | ASnowMobile::IsTracked, APlayerTank::IsTracked, AMenuSoundPriv::IsOneShot | infill-count | AUltraLite slot 8 (PS2 002e48e8, row 8747, infill-exact); ASnowMobile slot 7 (PS2 002ef6a8, row 8874, infill-count); APlayerTank slot 7 (PS2 002f13e0, row 8924, infill-exact) ... |
| 000d3580 | dummyNullFunction | AICharacter::DoWalking | AICharacter::DoWandering, AICharacter::DoAvoiding, AICharacter::DoStartled, AICharacter::DoDodging +40 | infill-count | AICharacterBond slot 10 (PS2 001197a8, row 423, infill-exact); AICharacterBond slot 11 (PS2 001197b0, row 424, infill-exact); AICharacterBond slot 12 (PS2 001197b8, row 425, infill-exact) ... |
| 000f70e0 | FUN_000f70e0 | EAGLAnim::FnAnim::FindMatchTime | EAGLAnim::FnAnim::EvalPhase, EAGLAnim::FnAnim::EvalVel2D, EAGLAnim::FnAnim::EvalWeights, EAGLAnim::FnAnim::EvalState | infill-exact-run | EAGLAnim::FnPoseMirror slot 5 (PS2 0026dfd8, row 6588, infill-exact-run); EAGLAnim::FnPoseMirror slot 7 (PS2 0026dfe8, row 6590, infill-exact-run); EAGLAnim::FnPoseMirror slot 8 (PS2 0026dff0, row 6591, infill-exact-run) ... |
| 000f7310 | FUN_000f7310 | EAGLAnim::FnAnim::EvalSQT | EAGLAnim::FnAnim::FindTime | infill-exact-run | EAGLAnim::FnPoseMirror slot 12 (PS2 0026e010, row 6595, infill-exact-run); EAGLAnim::FnRawLinearChannel slot 6 (PS2 0026dfe0, row 6589, infill-exact-run); EAGLAnim::FnRawLinearChannel slot 12 (PS2 0026e010, row 6595, infill-exact-run) ... |
| 000f7330 | dummyGetNullValue | AICharacter::GetVehiclePtr | EAGLAnim::FnAnim::GetPhaseChan | infill-exact-run | AICharacterBond slot 3 (PS2 001196c8, row 404, ghidra); AICharacterEnemyDriver slot 3 (PS2 001196c8, row 404, ghidra); AICharacterPassenger slot 3 (PS2 001196c8, row 404, ghidra) ... |

## confirmed (41)

| Xbox | now | proposed | also (folded) | weakest PS2 source | evidence |
|---|---|---|---|---|---|
| 0001c200 | AICharacter::IsAlive | AICharacter::IsAlive |  | ghidra | AICharacterBond slot 1 (PS2 00119638, row 392, ghidra); AICharacterEnemyDriver slot 1 (PS2 00119638, row 392, ghidra); AICharacterPassenger slot 1 (PS2 00119638, row 392, ghidra) ... |
| 0001c220 | AICharacter::SetActor | AICharacter::SetActor |  | infill-exact | AICharacterBond slot 2 (PS2 00119698, row 401, infill-exact); AICharacterEnemyDriver slot 2 (PS2 00119698, row 401, infill-exact); AICharacterPassenger slot 2 (PS2 00119698, row 401, infill-exact) |
| 0001c5b0 | AICharacter::GetZoneHitPointScale | AICharacter::GetZoneHitPointScale |  | ghidra | AICharacterBond slot 5 (PS2 001190f8, row 376, ghidra); AICharacterEnemyDriver slot 5 (PS2 001190f8, row 376, ghidra); AICharacterPassenger slot 5 (PS2 001190f8, row 376, ghidra) ... |
| 0001c690 | AICharacter::NotifyZoneDamage | AICharacter::NotifyZoneDamage |  | ghidra | AICharacterBond slot 4 (PS2 00118eb0, row 374, ghidra); AICharacterEnemyDriver slot 4 (PS2 00118eb0, row 374, ghidra); AICharacterPassenger slot 4 (PS2 00118eb0, row 374, ghidra) ... |
| 0001cbf0 | AICharacterBond::~AICharacterBond | AICharacterBond::~AICharacterBond |  | ghidra | called by AICharacterBond::scalar_deleting_destructor (0x0001cc00) and stores AICharacterBond's vtable |
| 0001cc00 | AICharacterBond::scalar_deleting_destructor | AICharacterBond::scalar_deleting_destructor |  | ghidra | AICharacterBond slot 0 (PS2 0011a288, row 455, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0001d090 | AICharacterBond::DoNeutral | AICharacterBond::DoNeutral |  | infill-exact-run | AICharacterBond slot 8 (PS2 0011a318, row 457, infill-exact-run) |
| 0001d0c0 | AICharacterBond::DoInitial | AICharacterBond::DoInitial | AICharacterBond::DoIdling, AICharacterPassenger::DoInitial, AICharacterPassenger::DoIdling | infill-exact-run | AICharacterBond slot 7 (PS2 0011a2e0, row 456, infill-exact-run); AICharacterBond slot 9 (PS2 0011a360, row 458, infill-exact-run); AICharacterPassenger slot 7 (PS2 00125790, row 648, infill-exact-run) ... |
| 0001d0e0 | AICharacterBond::DoArming | AICharacterBond::DoArming |  | infill-exact-run | AICharacterBond slot 15 (PS2 0011a398, row 459, infill-exact-run) |
| 0001d1c0 | AICharacterBond::DoFiring | AICharacterBond::DoFiring |  | infill-exact-run | AICharacterBond slot 18 (PS2 0011a4a0, row 460, infill-exact-run) |
| 0001d2a0 | AICharacterBond::HandleInterrupts | AICharacterBond::HandleInterrupts |  | infill-exact-run | AICharacterBond slot 30 (PS2 0011a5b8, row 461, infill-exact-run) |
| 00049eb0 | ECollision::~ECollision | ECollision::~ECollision |  | ghidra | called by ECollision::scalar_deleting_destructor (0x0004eb40) and stores ECollision's vtable |
| 0005dfa0 | Human::ApplyDamage | Human::ApplyDamage |  | sheet | Human slot 2 (PS2 0017f6c0, row 2485, sheet) |
| 00060a30 | Missile::Simulate | Missile::Simulate |  | sheet | Missile slot 4 (PS2 00181c80, row 2512, sheet) |
| 00060b70 | Newton::Simulate | Newton::Simulate |  | sheet | Newton slot 4 (PS2 00184098, row 2530, sheet) |
| 0006f3e0 | PhysicsObject::GetDamageZones | PhysicsObject::GetDamageZones |  | ghidra | Human slot 3 (PS2 00195960, row 2712, ghidra); Missile slot 3 (PS2 00195960, row 2712, ghidra); Newton slot 3 (PS2 00195960, row 2712, ghidra) ... |
| 0006f660 | PhysicsObject::DebugObject | PhysicsObject::DebugObject |  | sheet | Human slot 5 (PS2 00195d28, row 2728, sheet); Missile slot 5 (PS2 00195d28, row 2728, sheet); Newton slot 5 (PS2 00195d28, row 2728, sheet) ... |
| 0006f680 | PhysicsObject::~PhysicsObject | PhysicsObject::~PhysicsObject |  | ghidra | called by PhysicsObject::scalar_deleting_destructor (0x0006f7f0) and stores PhysicsObject's vtable |
| 0006f780 | PhysicsObject::ApplyDamage | PhysicsObject::ApplyDamage |  | sheet | Missile slot 2 (PS2 00195940, row 2711, sheet); PhysicsObject slot 2 (PS2 00195940, row 2711, sheet) |
| 00073150 | Sentry::~Sentry | Sentry::~Sentry |  | sheet | called by Sentry::scalar_deleting_destructor (0x000735d0) and stores Sentry's vtable |
| 00075420 | Smackable::Simulate | Smackable::Simulate |  | sheet | Smackable slot 4 (PS2 0019e718, row 2840, sheet) |
| 00075560 | Smackable::ApplyDamage | Smackable::ApplyDamage |  | sheet | Smackable slot 2 (PS2 0019e8c0, row 2841, sheet) |
| 00078540 | RCamera::SetActive | RCamera::SetActive |  | sheet | RCamera slot 1 (PS2 001a4578, row 2947, sheet); RPlayerCamera slot 1 (PS2 001a4578, row 2947, sheet); RWorldCamera slot 1 (PS2 001a4578, row 2947, sheet) |
| 0008cfa0 | RRenderWorldCamera::DoRender | RRenderWorldCamera::DoRender |  | sheet | RRenderWorldCamera slot 4 (PS2 001c55d8, row 3466, sheet) |
| 0008f9f0 | RSceneObj::Render | RSceneObj::Render |  | sheet | RSceneObj slot 2 (PS2 001c85f8, row 3518, sheet) |
| 00095560 | RVehicle::ResolveObjectData | RVehicle::ResolveObjectData |  | sheet | RVehicle slot 18 (PS2 001d2588, row 3697, sheet) |
| 000955b0 | RVehicle::SetViewDrawList | RVehicle::SetViewDrawList |  | sheet | RVehicle slot 1 (PS2 001d2618, row 3698, sheet) |
| 000955d0 | RVehicle::PostLoad | RVehicle::PostLoad |  | sheet | RVehicle slot 17 (PS2 001d2648, row 3699, sheet) |
| 000965e0 | RVehicle::Render | RVehicle::Render |  | sheet | RVehicle slot 2 (PS2 001d35d8, row 3717, sheet) |
| 00097ee0 | RWorldCamera::RestartCamera | RWorldCamera::RestartCamera |  | sheet | RWorldCamera slot 4 (PS2 001d5538, row 3793, sheet) |
| 000dc420 | GHud::~GHud | GHud::~GHud |  | sheet | called by GHud::scalar_deleting_destructor (0x000e00d0) and stores GHud's vtable |
| 000e3730 | GSubtitles::~GSubtitles | GSubtitles::~GSubtitles |  | sheet | called by GSubtitles::scalar_deleting_destructor (0x000e3ad0) and stores GSubtitles's vtable |
| 000faab0 | EAGLAnim::FnAnimMemoryMap::SetAnimMemoryMap | EAGLAnim::FnAnimMemoryMap::SetAnimMemoryMap |  | ghidra | EAGLAnim::FnRawLinearChannel slot 15 (PS2 0026dd48, row 6554, ghidra); EAGLAnim::FnRawStateChan slot 15 (PS2 0026dd48, row 6554, ghidra); EAGLAnim::FnAnimMemoryMap slot 15 (PS2 0026dd48, row 6554, ghidra) |
| 000faae0 | EAGLAnim::FnAnimMemoryMap::GetTargetCheckSum | EAGLAnim::FnAnimMemoryMap::GetTargetCheckSum |  | ghidra | EAGLAnim::FnRawLinearChannel slot 1 (PS2 0026dd60, row 6557, ghidra); EAGLAnim::FnRawEventChannel slot 1 (PS2 0026dd60, row 6557, ghidra); EAGLAnim::FnRawStateChan slot 1 (PS2 0026dd60, row 6557, ghidra) ... |
| 00117e40 | UGroup::Processor::StartGroup | UGroup::Processor::StartGroup |  | infill-exact-run | CARP::SymbolicResolver slot 0 (PS2 002dc150, row 8560, infill-exact-run) |
| 00118b40 | CARP::SymbolicResolver::ProcessData | CARP::SymbolicResolver::ProcessData |  | sheet | CARP::SymbolicResolver slot 1 (PS2 002dc1b0, row 8562, sheet) |
| 0011c710 | ABaseSound::GetName | ABaseSound::GetName |  | ghidra | AVehicle slot 2 (PS2 002fcb80, row 9109, ghidra); AUltraLite slot 2 (PS2 002fcb80, row 9109, ghidra); AStream slot 2 (PS2 002fcb80, row 9109, ghidra) ... |
| 0011f9b0 | AVehicle::Play | AVehicle::Play |  | sheet | AVehicle slot 3 (PS2 002e1768, row 8690, sheet) |
| 00122580 | AStream::Play | AStream::Play |  | ghidra | AStream slot 3 (PS2 002e8c70, row 8791, ghidra) |
| 0012d6f0 | ASentry::Play | ASentry::Play |  | sheet | ASentry slot 3 (PS2 002eff88, row 8888, sheet) |
| 00131d35 | __pure_virtual | __pure_virtual |  | ghidra | AIVehicle slot 1 (PS2 0024c5d0, row 5533, ghidra); AIVehicle slot 2 (PS2 0024c5d0, row 5533, ghidra); AIVehicle slot 3 (PS2 0024c5d0, row 5533, ghidra) ... |

